# IB-Cluster Culling Refactor Handoff

This document is a future-session implementation brief for refining static mesh
frustum culling from the current mesh/subset granularity to spatial
index-buffer cluster granularity.

The immediate motivation is Sponza: the debug overlay can report all parent
meshes visible, for example `10/10`, while the engine still skips some draw
work. The overlay currently makes this confusing because it emphasizes parent
`MeshInfo` visibility and only reports subsets as "drawn / total"; it does not
separately report subset or future cluster culling.

Do not start by rewriting the renderer. The right path is incremental:

1. Make current culling stats truthful and visible.
2. Add shared static submesh clusters to `MeshAsset`.
3. Cull clusters per render entity using the entity transform.
4. Draw visible cluster index ranges under the existing material/subset bind
   path.
5. Validate draw counts, visual parity, and performance.

## Repository and branch context

Project root:

```text
D:\Code\Game\T850\T850
```

Git repository root:

```text
D:\Code\Game\T850
```

Recent relevant pushed commit:

```text
dcf26f5 Fix cross-API PBR rendering parity
```

At the time this handoff was written, the user had local-only assets that
must not be committed:

```text
T850\Assets\Models\2021_bmw_m4_competition.glb
T850\Assets\Models\rewind_cyborg.glb
T850\Assets\Models\space_station_scene_hd.glb
glTF-Sample-Renderer\
T850\config.json   (local runtime config pointing at local models)
```

## Current architecture relevant to this work

The renderer is mid-refactor from old `RenderMesh`-owned geometry/material
state into shared assets plus per-instance render entities.

Current important types:

```text
RenderMesh / PrimitiveBase instance
  Info: vector<MeshInfo>
    MeshInfo
      VB / shared vbPoolAlloc
      bounds
      SubSets: vector<SubSetInfo>
        SubSetInfo
          material fields / matAsset
          bounds
          ibPoolAlloc
          NumTris / NumVertex / TriStart

MeshAsset
  sourcePath
  rootAABB
  submeshes: vector<Submesh>
    Submesh
      vertexStart / vertexCount
      indexStart / triangleCount
      materialSlot
      localAABB
      vbAlloc / ibAlloc

RenderEntity
  mesh: MeshAsset*
  materialOverrides
  worldFromLocal
  worldAABB
  visible
```

Files:

```text
T850\Framework\include\scene\MeshAsset.h
T850\Framework\include\scene\MaterialAsset.h
T850\Framework\include\scene\RenderQueue.h
T850\Framework\include\scene\RenderMesh.h
T850\Framework\src\scene\RenderMesh.cpp
T850\Framework\src\scene\RenderSkinnedMesh.cpp
T850\Framework\src\scene\RenderGraph.cpp
T850\DayScene\DayScene.cpp
T850\DayScene\SandboxScene.cpp
```

### `MeshAsset` today

`MeshAsset` is the shared immutable geometry metadata container. GPU memory
lives in shared vertex/index pools owned by `MeshAssetCache`; `MeshAsset` keeps
CPU metadata and submesh ranges.

Current `Submesh` in `MeshAsset.h`:

```cpp
struct Submesh {
  uint32_t vertexStart   = 0;
  uint32_t vertexCount   = 0;
  uint32_t indexStart    = 0;   // in indices, not triangles
  uint32_t triangleCount = 0;
  uint32_t materialSlot  = 0;
  bool     ib32Bit       = false;
  AABB     localAABB;
  ShaderKey vertexAttribKey;
  PoolAlloc vbAlloc;
  PoolAlloc ibAlloc;
  uint16_t vbPoolId      = 0xFFFFu;
  uint16_t ibPoolId      = 0xFFFFu;
};
```

This is already close to the desired shape: each `Submesh` is a drawable
material/index range, and each one already has `localAABB` plus a shared
index-pool allocation.

### `RenderEntity` today

`RenderQueue.h` already defines `RenderEntity`, but the main `RenderMesh::Draw`
path still drives rendering directly. This means the cluster work should be
implemented in the current `RenderMesh` path first, while shaping the data so
it naturally migrates to `RenderEntity` / `RenderQueue` later.

Current `RenderEntity`:

```cpp
struct RenderEntity {
  uint32_t                    id              = 0;
  MeshAsset*                  mesh            = nullptr;
  std::vector<MaterialAsset*> materialOverrides;
  XMATRIX44                   worldFromLocal;
  AABB                        worldAABB;
  uint8_t                     layerMask       = 0xFF;
  bool                        visible         = true;
};
```

The desired ownership split for clusters:

```text
MeshAsset owns:
  static local-space cluster AABBs
  static cluster index ranges
  cluster-to-submesh/material relationship

RenderEntity / RenderMesh instance owns:
  world transform
  per-frame visibility result
  material overrides
  per-instance constants
```

## Current culling behavior

The current static `RenderMesh::Draw` path already has two frustum culling
levels.

### Level 1: parent geometry / `MeshInfo` culling

In `RenderMesh::Draw`, the engine builds a visibility mask for
`numGeometries = xFile->MeshInfo.size()`:

```cpp
std::vector<uint8_t> visible(numGeometries, 0);

if (static_cast<int>(numGeometries) >= kParallelCullThreshold && g_threadPool) {
  XMATRIX44 worldCopy = transform;
  g_threadPool->ParallelFor(0, static_cast<int>(numGeometries), [&](int i) {
    visible[i] = AABBInsideFrustum(Info[i].bounds, worldCopy, frustumPlanes) ? 1 : 0;
  });
} else {
  for (std::size_t i = 0; i < numGeometries; i++) {
    visible[i] = AABBInsideFrustum(Info[i].bounds, transform, frustumPlanes) ? 1 : 0;
  }
}
```

If a parent `MeshInfo` is outside, the whole geometry is skipped:

```cpp
m_totalSubsets += static_cast<int>(it_MeshInfo->SubSets.size());

if (!visible[i]) {
  m_culledMeshes++;
  continue;
}
```

This is what the DayScene overlay reports as:

```text
Sponza meshes: visible / total
```

### Level 2: current subset culling

Inside a visible `MeshInfo`, each subset is tested before material binding and
draw:

```cpp
if (!ShouldDrawSubsetInPass(*sub_info, currentPass))
  continue;

// Per-subset frustum cull
if (!AABBInsideFrustum(sub_info->bounds, transform, frustumPlanes))
  continue;
```

So the engine already culls at the current subset/material/index-range level.
The issue is that the overlay does not expose subset culling separately. It only
tracks:

```cpp
mutable int m_totalSubsets = 0;
mutable int m_drawnSubsets = 0;
mutable int m_culledMeshes = 0;
```

There is no `m_culledSubsets`, `m_visibleSubsets`, `m_totalClusters`, or
`m_drawnClusters` yet.

### Current draw call

For a visible subset, the draw currently uses the subset's shared IB allocation
if available:

```cpp
if (sub_info->ibPoolAlloc.IsValid() && it_MeshInfo->vbPoolAlloc.IsValid()) {
  T8DeviceContext->DrawIndexed(sub_info->ibPoolAlloc.count,
                               sub_info->ibPoolAlloc.offsetElems,
                               it_MeshInfo->vbPoolAlloc.offsetElems);
} else {
  T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
}
```

Important: the `DrawIndexed` `startIndexLocation` is already an offset into the
shared IB pool. This means finer culling should not create separate GPU buffers.
It should create smaller ranges inside the same pool and draw those ranges.

## What "IB-level granularity" should mean

Do not interpret this as "one new index buffer per culling unit." That would
increase resource count and bind churn.

The intended interpretation is:

```text
Keep one shared index pool.
Split a material subset's index range into multiple spatial clusters.
Each cluster is a contiguous index range in the shared index pool.
Each cluster has a local-space AABB.
Cull each cluster's AABB.
Draw only visible cluster ranges with DrawIndexed(count, offset, baseVertex).
```

Conceptually:

```text
MeshAsset
  Submesh 0: material A, old subset range
    Cluster 0: AABB + index offset/count
    Cluster 1: AABB + index offset/count
    Cluster 2: AABB + index offset/count
  Submesh 1: material B
    Cluster 3: AABB + index offset/count
    Cluster 4: AABB + index offset/count
```

This is finer than today's `SubSetInfo` AABB. If a material subset spans many
disconnected Sponza pieces, its current AABB can be very large; cluster AABBs
can be tight and cull much more work.

## Why this fits the entity/mesh separation

Cluster data is static and reusable:

```text
source mesh file -> MeshAsset -> SubmeshCluster list
```

Visibility is per instance:

```text
RenderEntity transform + camera frustum -> visible cluster set
```

This is exactly the separation we want:

```text
MeshAsset:
  immutable geometry ranges and local AABBs

MaterialAsset:
  immutable material/shader/texture data

RenderEntity / RenderMesh instance:
  transform, visibility, pass-specific draw extraction

RenderQueue / DrawItem:
  future flat, sorted execution list
```

The first implementation can stay in `RenderMesh::Draw` because that is the
active path. Shape the data as if `RenderQueue` will consume it later.

## Proposed data model

Add a cluster structure to `MeshAsset.h`.

Recommended names:

```cpp
struct SubmeshCluster {
  uint32_t submeshIndex = 0;       // parent MeshAsset::submeshes index
  uint32_t indexOffset  = 0;       // offset relative to submesh.ibAlloc.offsetElems
  uint32_t indexCount   = 0;       // number of indices, multiple of 3
  AABB     localAABB;             // bounds of referenced vertices
};
```

Then extend `Submesh`:

```cpp
struct Submesh {
  ...
  uint32_t firstCluster = 0;
  uint32_t clusterCount = 0;
};
```

And extend `MeshAsset`:

```cpp
struct MeshAsset {
  ...
  std::vector<SubmeshCluster> clusters;
};
```

Why offsets should be relative to the submesh allocation:

```text
absoluteStartIndex = submesh.ibAlloc.offsetElems + cluster.indexOffset
```

This keeps cluster metadata stable even if pool packing changes, and it mirrors
how `Submesh` already treats `indexStart` as logical metadata while `ibAlloc`
is the physical pool location.

### Do not put material data on clusters

Clusters should inherit material/state from the parent submesh/subset. Material
data still belongs to `SubSetInfo` / `MaterialAsset`.

Do not add texture or shader pointers to `SubmeshCluster`.

### Do not duplicate vertex buffers

Clusters reuse the parent `MeshInfo` / submesh vertex allocation.

For current `RenderMesh::Draw`:

```cpp
baseVertex = it_MeshInfo->vbPoolAlloc.offsetElems;
startIndex = sub_info->ibPoolAlloc.offsetElems + cluster.indexOffset;
count      = cluster.indexCount;
```

## Cluster generation strategy

The cluster builder must preserve material boundaries. Never cluster across
subsets with different materials, shader keys, alpha mode, double-sided state,
or render pass classification.

Input:

```text
SubSetInfo
  NumTris
  NumVertex
  TriStart
  ibPoolAlloc
  bounds
  material/shader key
```

Output:

```text
N clusters for that subset, each with:
  contiguous local index range
  local AABB
```

### Minimum viable clustering

Start with a conservative, simple builder:

```text
if subset triangle count <= threshold:
  create one cluster equal to the whole subset
else:
  split triangles into fixed-size chunks, e.g. 128 or 256 triangles per cluster
  compute each chunk AABB from referenced vertices
```

This is easy but not ideal. Fixed sequential chunks may not be spatially tight
if the source triangles are not spatially ordered. However it is a safe first
step and validates the plumbing.

Suggested constants:

```cpp
constexpr uint32_t kMinTrianglesForClustering = 256;
constexpr uint32_t kTargetTrianglesPerCluster = 128;
```

Rules:

```text
cluster.indexCount must be a multiple of 3
cluster.indexCount must be > 0
cluster AABB must include all referenced vertices
sum(cluster.indexCount) == submesh.ibAlloc.count
```

### Better spatial clustering

After the simple version works, improve quality:

1. For each triangle, compute centroid from referenced vertex positions.
2. Split by longest axis of centroid bounds.
3. Recursively partition until each cluster has <= target triangle count.
4. Emit clusters in the new order.
5. Rebuild/repack the subset's index data so each cluster is contiguous.

This produces tighter AABBs and better culling than sequential chunks.

Be careful: if you reorder triangles for clusters, the shared IB pool upload
must use the reordered index sequence. Do not generate cluster ranges that
point into a non-contiguous scattered set of indices.

### Recommended first implementation

Use a two-stage approach:

```text
Stage A:
  no triangle reordering
  clusters are contiguous chunks of existing subset IB range
  validate rendering and stats

Stage B:
  optional spatial reorder inside each subset before IB pool upload
  validate again
```

Do not combine both in the first change.

## Where to generate clusters

`RenderMesh::Create` currently populates `m_asset->submeshes` after per-subset
pool allocations are known. Relevant code is around the section that does:

```cpp
Submesh sub;
sub.vertexStart   = s.VertexStart;
sub.vertexCount   = s.NumVertex;
sub.indexStart    = s.TriStart * 3u;
sub.triangleCount = s.NumTris;
sub.materialSlot  = static_cast<uint32_t>(m_asset->submeshes.size());
sub.ib32Bit       = s.IB32Bit;
sub.localAABB     = ...
sub.vbAlloc       = ...
sub.ibAlloc       = ...
m_asset->submeshes.push_back(sub);
```

This is the right place to also build `m_asset->clusters`.

Pseudo-code:

```cpp
uint32_t submeshIndex = static_cast<uint32_t>(m_asset->submeshes.size());
Submesh sub = ...;

sub.firstCluster = static_cast<uint32_t>(m_asset->clusters.size());
BuildClustersForSubset(mi, s, submeshIndex, m_asset->clusters);
sub.clusterCount = static_cast<uint32_t>(m_asset->clusters.size()) - sub.firstCluster;

m_asset->submeshes.push_back(sub);
```

But note: `BuildClustersForSubset` needs access to:

```text
subset index data
parent vertex data
vertex stride
index format
```

Those are available earlier in `RenderMesh::Create` while building VBs/IBs and
computing subset AABBs. If the data is no longer directly available at the
`m_asset->submeshes.push_back` site, add a temporary CPU-side per-subset cluster
vector during the earlier loop and copy it into `MeshAsset` later.

## Current AABB computation

Subset AABBs are already computed from referenced vertices in
`RenderMesh::Create`:

```cpp
it_subsetinfo->bounds.Expand(vertexPosition);
```

There are separate paths for 16-bit and 32-bit index data. Follow the same
index decoding to compute cluster AABBs.

Do not use only `SubSetInfo::bounds` for all clusters; that defeats the purpose.

## Draw path changes

In `RenderMesh::Draw`, after a subset passes material/pass classification and
subset AABB culling, draw clusters if available.

Current subset draw:

```cpp
if (sub_info->ibPoolAlloc.IsValid() && it_MeshInfo->vbPoolAlloc.IsValid()) {
  T8DeviceContext->DrawIndexed(sub_info->ibPoolAlloc.count,
                               sub_info->ibPoolAlloc.offsetElems,
                               it_MeshInfo->vbPoolAlloc.offsetElems);
} else {
  T8DeviceContext->DrawIndexed(sub_info->NumVertex, 0, 0);
}
```

Proposed cluster draw helper:

```cpp
auto drawIndexedRange = [&](uint32_t indexCount, uint32_t startIndex, uint32_t baseVertex) {
  T8DeviceContext->DrawIndexed(indexCount, startIndex, baseVertex);
};
```

Proposed logic:

```cpp
const bool canUseClusterPath =
  m_asset &&
  submeshIndex < m_asset->submeshes.size() &&
  sub_info->ibPoolAlloc.IsValid() &&
  it_MeshInfo->vbPoolAlloc.IsValid();

if (canUseClusterPath) {
  const Submesh& submesh = m_asset->submeshes[submeshIndex];
  uint32_t drawnForSubset = 0;

  for each cluster in submesh.clusters:
    if (!AABBInsideFrustum(cluster.localAABB, transform, frustumPlanes)) {
      m_culledClusters++;
      continue;
    }

    DrawIndexed(cluster.indexCount,
                sub_info->ibPoolAlloc.offsetElems + cluster.indexOffset,
                it_MeshInfo->vbPoolAlloc.offsetElems);
    m_drawnClusters++;
    drawnForSubset++;

  if (drawnForSubset > 0)
    m_drawnSubsets++;
} else {
  existing subset DrawIndexed fallback
}
```

### Need a stable mapping from `SubSetInfo` to `MeshAsset::Submesh`

Current code flattens submeshes in order and copies pool offsets back to
`SubSetInfo`:

```cpp
std::size_t flatIdx = 0;
for each MeshInfo:
  for each SubSetInfo:
    mi.SubSets[j].ibPoolAlloc = m_asset->submeshes[flatIdx].ibAlloc;
    ++flatIdx;
```

Cluster drawing needs that same `flatIdx`. Do not recompute it expensively or
fragilely in the inner draw loop.

Recommended addition to `SubSetInfo`:

```cpp
uint32_t meshAssetSubmeshIndex = UINT32_MAX;
```

Populate it in the same copy-back loop:

```cpp
mi.SubSets[j].meshAssetSubmeshIndex = static_cast<uint32_t>(flatIdx);
mi.SubSets[j].ibPoolAlloc = m_asset->submeshes[flatIdx].ibAlloc;
```

Then the draw path can directly access:

```cpp
const Submesh& submesh = m_asset->submeshes[sub_info->meshAssetSubmeshIndex];
```

## Stats and overlay changes

Before cluster drawing, fix the current overlay.

Add stats to `RenderMesh`:

```cpp
mutable int m_totalMeshes = 0;
mutable int m_visibleMeshes = 0;
mutable int m_culledMeshes = 0;

mutable int m_totalSubsets = 0;
mutable int m_visibleSubsets = 0;
mutable int m_culledSubsets = 0;
mutable int m_drawnSubsets = 0;

mutable int m_totalClusters = 0;
mutable int m_visibleClusters = 0;
mutable int m_culledClusters = 0;
mutable int m_drawnClusters = 0;
```

If keeping changes smaller, at least add:

```cpp
mutable int m_culledSubsets = 0;
mutable int m_totalClusters = 0;
mutable int m_drawnClusters = 0;
mutable int m_culledClusters = 0;
```

Update DayScene overlay in:

```text
T850\DayScene\DayScene.cpp
T850\DayScene\SandboxScene.cpp
```

Current overlay:

```cpp
snprintf(buf, sizeof(buf), "Sponza meshes: %d/%zu  Culled: %d  Subsets drawn: %d/%d",
         (int)rm->Info.size() - rm->m_culledMeshes, rm->Info.size(),
         rm->m_culledMeshes, rm->m_drawnSubsets, rm->m_totalSubsets);
```

Recommended overlay:

```text
Sponza meshes: visible/total, culled N
Sponza subsets: visible/total, culled N, drawn N
Sponza clusters: visible/total, culled N, drawn N
```

If line space is limited:

```text
Meshes 10/10 culled 0 | Subsets 18/27 culled 9 | Clusters 92/240 culled 148
```

## Pass-specific behavior

Do not blindly apply all cluster culling to every pass without thinking about
visibility semantics.

### GBuffer pass

Safe and primary target.

Use camera frustum planes. Draw only visible clusters.

### Forward transparent pass

More delicate. Transparent rendering currently sorts subsets:

```cpp
if (currentPass == PassType::FORWARD) {
  group by forward type
  sort by distance descending
}
```

If clusters are used for transparent subsets, they also need distance sorting,
not just parent subset sorting. Options:

1. Initially disable cluster splitting for transparent subsets.
2. Or cull clusters but draw visible clusters in back-to-front order by cluster
   AABB center distance.

Recommended first version:

```text
Use clusters for opaque static subsets only.
Keep transparent / transmission / alpha blend subsets on old subset path.
```

This avoids introducing blending regressions.

### Shadow map pass

Be careful. Camera-frustum culling is not the same as light-frustum culling.

The current draw path extracts `pActualCamera->VP`, which is probably the main
camera. If the shadow pass uses a light camera elsewhere, verify before using
cluster culling there.

Recommended first version:

```text
Use cluster culling in GBuffer only.
For shadow/radial depth, keep current subset path until pass frustum ownership is clear.
```

Then add cluster culling for shadow only when using the correct light frustum.

### Skinned meshes

Do not apply this to `RenderSkinnedMesh` initially.

Skinned meshes were previously made unsafe for static CPU frustum culling
because GPU-skinned animated poses can move outside bind-pose AABBs. Cluster
culling would be even less safe unless using conservative animated bounds or
CPU-updated/skinned bounds.

Keep this refactor scoped to static `RenderMesh`.

## Implementation phases

### Phase 0: clean stats only

Goal: prove what the engine already does today.

Steps:

1. Add `m_culledSubsets` and optionally `m_visibleSubsets`.
2. Reset stats at top of `RenderMesh::Draw`.
3. Increment:
   - `m_totalSubsets` for every subset.
   - `m_culledSubsets` when subset AABB fails.
   - `m_visibleSubsets` when subset AABB passes.
   - `m_drawnSubsets` after successful draw.
4. Update `DayScene.cpp` and `SandboxScene.cpp` overlays.
5. Build and run Sponza.

Expected result:

```text
When looking away, "meshes visible" may still be 10/10, but subset culled count
should increase and drawn subsets should drop.
```

This phase is very low risk and should be done first.

### Phase 1: add cluster metadata with one cluster per subset

Goal: add structures without changing behavior.

Steps:

1. Add `SubmeshCluster`.
2. Add `firstCluster` and `clusterCount` to `Submesh`.
3. Add `std::vector<SubmeshCluster> clusters` to `MeshAsset`.
4. When populating `MeshAsset`, create exactly one cluster per submesh:

```cpp
cluster.submeshIndex = submeshIndex;
cluster.indexOffset = 0;
cluster.indexCount = sub.ibAlloc.count;
cluster.localAABB = sub.localAABB;
```

5. Add `meshAssetSubmeshIndex` to `SubSetInfo`.
6. Populate it during copy-back.
7. Do not change drawing yet.

Validation:

```text
Build succeeds.
Render output unchanged.
Stats can report totalClusters == totalSubsets.
```

### Phase 2: draw via clusters, still one cluster per subset

Goal: prove the cluster draw path is correct before increasing granularity.

Steps:

1. In `RenderMesh::Draw`, after subset culling/material binding, draw cluster
   ranges when available.
2. With one cluster per subset, output must be visually identical.
3. Stats should show:

```text
drawnClusters == drawnSubsets
totalClusters == totalSubsets
```

Validation:

```text
Sponza frame dump matches previous output.
D3D11, D3D12, Vulkan, GL do not crash.
Draw counts unchanged except for new stats.
```

### Phase 3: split opaque subsets into fixed-size contiguous clusters

Goal: add actual finer granularity with minimal risk.

Steps:

1. For each eligible static opaque subset:
   - if triangle count <= threshold: one cluster
   - else split into chunks of `kTargetTrianglesPerCluster`
2. Compute cluster local AABBs from referenced vertices.
3. Use the current existing index order. Do not reorder indices yet.
4. Use cluster path only for:
   - static `RenderMesh`
   - GBuffer pass
   - opaque subsets
5. Keep old subset path for:
   - transparent / transmission / blend
   - skinned meshes
   - shadow/radial depth until light frustum behavior is verified

Validation:

```text
totalClusters > totalSubsets for Sponza.
Looking away should reduce drawnClusters more than drawnSubsets.
No visual holes when moving camera.
No index offset explosions on D3D12/Vulkan.
```

### Phase 4: optional spatial reorder for tighter clusters

Goal: improve culling quality.

Steps:

1. Build a temporary list of triangles inside each subset:

```cpp
struct TriangleBuildItem {
  uint32_t i0, i1, i2;
  Vec3 centroid;
  AABB bounds;
};
```

2. Recursively split by longest centroid axis until target size.
3. Emit clusters in spatial order.
4. Rebuild the subset's index sequence in that cluster order before uploading
   to the shared index pool.
5. Set cluster offsets to the emitted contiguous ranges.

Validation:

```text
Same visuals as Phase 3.
Better culling ratio in Sponza.
No material leakage because clustering never crosses subset boundary.
```

## Key pitfalls

### 1. Do not increment `m_drawnSubsets` per cluster

Keep subset and cluster counters separate.

Recommended:

```cpp
bool drewAnyClusterForSubset = false;
...
if (cluster drawn)
  drewAnyClusterForSubset = true;
...
if (drewAnyClusterForSubset)
  m_drawnSubsets++;
```

### 2. Do not bind material per cluster

Bind material/shader/textures once at the subset level. Then draw all visible
clusters for that subset. This keeps state churn low.

### 3. Do not cluster across material boundaries

Even if two adjacent subsets share the same material, keep the first version
inside subset boundaries. Material dedup and state sorting can optimize later.

### 4. Do not use camera frustum for all passes by default

GBuffer is safe. Shadow/radial depth must be checked for correct light/camera
frustum. Transparent needs sorting.

### 5. Base vertex and start index must remain exact

For shared pools:

```text
baseVertex = MeshInfo.vbPoolAlloc.offsetElems
startIndex = SubSetInfo.ibPoolAlloc.offsetElems + cluster.indexOffset
count      = cluster.indexCount
```

Do not use `Submesh::indexStart` as a GPU pool offset.

### 6. The fallback path must remain

There are still legacy paths:

```text
SubSetInfo::IB
MeshInfo::VB
DrawIndexed(sub_info->NumVertex, 0, 0)
```

Keep them working when pool allocations or cluster metadata are missing.

### 7. OpenGL path uses different constant-buffer binding behavior

In `RenderMesh::Draw`, GL updates the old combined CB:

```cpp
it_MeshInfo->CB->UpdateFromBuffer(...);
it_MeshInfo->CB->Set(...);
```

D3D/Vulkan use split frame/instance/material CBs. Cluster drawing should not
change CB layout or binding.

## Suggested code touch list

Phase 0:

```text
T850\Framework\include\scene\RenderMesh.h
T850\Framework\src\scene\RenderMesh.cpp
T850\DayScene\DayScene.cpp
T850\DayScene\SandboxScene.cpp
```

Phase 1:

```text
T850\Framework\include\scene\MeshAsset.h
T850\Framework\include\scene\RenderMesh.h
T850\Framework\src\scene\RenderMesh.cpp
```

Phase 2:

```text
T850\Framework\src\scene\RenderMesh.cpp
```

Phase 3:

```text
T850\Framework\src\scene\RenderMesh.cpp
T850\Framework\include\scene\MeshAsset.h
```

Optional later RenderQueue migration:

```text
T850\Framework\include\scene\RenderQueue.h
T850\Framework\src\scene\RenderQueue.cpp
T850\Framework\src\scene\RenderGraph.cpp
```

## Validation plan

Build:

```powershell
Set-Location -Path 'D:\Code\Game\T850\T850'
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe' build\T850.sln /m /p:Configuration=Release /p:Platform=x64 /t:DayScene /verbosity:minimal
```

Runtime smoke targets:

```text
Sponza DayScene
Dragon.glb
rewind_cyborg.glb if locally present, but do not commit it
BMW model if locally present, but do not commit it
```

API smoke:

```text
D3D11
D3D12
Vulkan
GL
```

Expected validation for Phase 0:

```text
Overlay reports subset culling separately.
No render output changes expected.
```

Expected validation for Phase 1 and 2:

```text
Render output identical.
totalClusters == totalSubsets.
drawnClusters == drawnSubsets.
```

Expected validation for Phase 3:

```text
totalClusters > totalSubsets for Sponza.
drawnClusters drops when looking away.
No visible holes in Sponza.
No exploded geometry or wrong index offsets on D3D12/Vulkan.
```

Use frame dumps if there is any doubt:

```powershell
Set-Location -Path 'D:\Code\Game\T850\T850\bin\x64\Release'
.\DayScene.exe --api d3d12 --width 1280 --height 720 --scene 0 --dump-frame 3 --logLevel error
.\DayScene.exe --api vulkan --width 1280 --height 720 --scene 0 --dump-frame 3 --logLevel error
```

Then compare with:

```powershell
Set-Location -Path 'D:\Code\Game\T850\T850'
python scripts\t850_snapshot_mcp.py compare-snapshots <d3d12_dump_dir> <vulkan_dump_dir> --tolerance 4
```

## Performance metrics to add or collect

At minimum:

```text
mesh total / visible / culled
subset total / visible / culled / drawn
cluster total / visible / culled / drawn
draw call count
triangle/index count submitted
```

Better:

```text
CPU culling time
CPU draw submission time
visible index count vs total index count
```

Recommended derived metrics:

```text
subset cull ratio = culledSubsets / totalSubsets
cluster cull ratio = culledClusters / totalClusters
index submit ratio = submittedIndices / totalIndices
```

These will answer whether cluster granularity is worth the additional draw
calls for Sponza.

## Future RenderQueue shape

Once cluster culling works in `RenderMesh::Draw`, it can migrate to draw-item
extraction:

```cpp
struct DrawItem {
  MeshAsset* mesh;
  uint32_t submeshIndex;
  uint32_t clusterIndex;
  MaterialAsset* material;
  uint32_t indexCount;
  uint32_t startIndex;
  uint32_t baseVertex;
  ShaderKey key;
  uint64_t sortKey;
};
```

Extraction:

```text
for each RenderEntity:
  cull entity root AABB
  for each MeshAsset::Submesh:
    cull submesh AABB
    bind/pass classify material
    for each cluster:
      cull cluster AABB
      push DrawItem
```

Sorting:

```text
opaque:
  pass, shader, material, vertex pool, index pool, depth front-to-back

transparent:
  pass, transparent group, depth back-to-front
```

Execution:

```text
iterate sorted DrawItem list
dedupe shader/material/textures/IB/VB with MeshDrawStateTracker
DrawIndexed(cluster.indexCount, cluster.startIndex, cluster.baseVertex)
```

This is the longer-term destination. Do not make the first cluster-culling
change depend on full RenderQueue migration.

## Recommended first pull request scope

Keep the first PR small:

```text
1. Add subset culling stats and overlay fixes.
2. Add MeshAsset cluster metadata with one cluster per subset.
3. Draw through the cluster path only when clusterCount > 0, but with one
   cluster per subset so visuals are identical.
```

Then second PR:

```text
4. Split opaque GBuffer subsets into fixed contiguous IB clusters.
5. Add cluster stats and perf counters.
```

Then optional third PR:

```text
6. Spatially reorder triangles within each subset for tighter clusters.
7. Consider shadow-pass and transparent cluster handling.
```

This sequencing isolates correctness risks from performance/quality work.
