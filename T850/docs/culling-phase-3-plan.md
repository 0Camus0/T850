# Culling Phase 3 Handoff

This document is the implementation brief for the next culling optimization pass. Phase 1 and Phase 2 are already implemented on branch `feature-005`; Phase 3 should be done separately from the current wrap-up.

## Current State

Project root:

```text
D:\Code\Game\T850\T850
```

Repository root:

```text
D:\Code\Game\T850
```

Build command:

```powershell
Set-Location -Path 'D:\Code\Game\T850\T850'
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Launch-VsDevShell.ps1' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
& 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe' build\T850.sln /m /p:Configuration=Release /p:Platform=x64 /t:DayScene /verbosity:minimal
```

Do not use CMake for this work. The known-good path is Visual Studio 2022 BuildTools MSBuild.

Local assets that must not be committed:

```text
T850\Assets\Models\2021_bmw_m4_competition.glb
T850\Assets\Models\Marauder.glb
T850\Assets\Models\Tyrant.glb
T850\Assets\Models\rewind_cyborg.glb
T850\Assets\Models\space_station_scene_hd.glb
```

Do not add those files to `.gitignore`; just leave them unstaged.

## Completed Before Phase 3

### Phase 1

Implemented in `Framework/src/scene/RenderMesh.cpp` and `Framework/include/scene/RenderMesh.h`:

- Reused hot visibility/order scratch buffers instead of allocating local vectors every draw.
- Replaced 8-corner AABB frustum testing with center/extent plane-radius testing.
- Added culling benchmark counters for mesh/subset/cluster tests, draw calls, state changes, indices, and culling CPU time.
- Gated the culling CPU timer to benchmark mode so normal rendering does not pay stopwatch overhead.

Benchmark aggregation was added in `DayScene/DayScene.cpp` and `DayScene/DayScene.h` under the `cullingStats` JSON node.

### Phase 2

Implemented in `RenderMesh`:

- Added `RenderMesh::FrustumResult` with `Outside`, `Intersecting`, and `Inside`.
- Added `ClassifyAABBFrustum` and kept `AABBInsideFrustum` as a compatibility wrapper.
- Propagated parent classification down the hierarchy:
  - mesh outside skips the mesh
  - mesh inside skips subset tests
  - subset inside skips cluster tests
  - intersecting parents still test children normally
- Removed the old global cluster prepass that tested every cluster up front.
- Cluster visibility is now computed lazily only when parent subset bounds intersect the frustum.

Runtime culling toggle now accepts both `6` and `KP6` in DayScene and SandboxScene.

## Phase 2 Baseline

Use these as the immediate comparison point for Phase 3.

Phase 1 D3D12 baseline:

```text
T850\benchmark_phase1_d3d12_20260504_103404
```

Phase 2 D3D12 runs:

```text
T850\benchmark_phase2_d3d12_20260504_105804
T850\benchmark_phase2_d3d12_reversed_20260504_110444
```

Key Phase 2 culling-on numbers from the reversed run:

```text
Avg frame:       9.5054 ms
FPS:             105.20
P95:             11.4363 ms
Draw calls:      474.18 per frame
Cluster tests:   1096.33 per frame
Culling CPU:     0.0422 ms per frame
```

The first Phase 2 pair had a very fast culling-off run and showed run-order variance. The reversed pair showed culling on faster by `0.82 ms` / `7.91%`. For Phase 3, use paired and reversed-pair D3D12 benchmarks before trusting a result.

## Phase 3 Goal

Phase 3 should reduce remaining CPU culling overhead without changing visual output or scene behavior.

The main target is to avoid repeated matrix/radius work and reduce per-child plane checks when parent bounds already tell us which frustum planes matter.

There are two recommended subphases:

```text
Phase 3A: bounds cache + plane-mask propagation
Phase 3B: optional spatial cluster reorder for better culling quality
```

Do Phase 3A first. Phase 3B changes mesh creation and index ordering, so keep it separate unless 3A is fully validated.

## Phase 3A: Bounds Cache And Plane Masks

### Problem

The current classifier is much cheaper than Phase 0/1, but every intersecting child test still recomputes:

```text
local center/extents from min/max
world center from matrix rows
world extents from abs(world) rows
six plane distance/radius tests
```

Phase 2 skips many tests, but the remaining cluster tests are still about `1096` per frame in the D3D12 benchmark. Each one repeats the same world-transform math for static Sponza geometry.

### Desired Shape

Add a compact bounds representation and a per-instance world-space cache.

Suggested structures in `RenderMesh.h`:

```cpp
struct BoundsCE {
  float cx, cy, cz;
  float ex, ey, ez;
};

struct FrustumClassifyResult {
  FrustumResult result = FrustumResult::Outside;
  uint8_t planeMask = 0x3F;
};
```

`BoundsCE` means center/extents. Local-space `BoundsCE` can live on immutable mesh/submesh/cluster metadata. World-space `BoundsCE` must live on the render instance because it depends on `RenderMesh::transform`.

Recommended instance cache fields on `RenderMesh`:

```cpp
mutable XMATRIX44 m_cullBoundsWorld;
mutable bool m_cullBoundsCacheValid = false;
mutable std::vector<BoundsCE> m_worldMeshBoundsScratch;
mutable std::vector<BoundsCE> m_worldSubsetBoundsScratch;
mutable std::vector<BoundsCE> m_worldClusterBoundsScratch;
```

Only rebuild the cache when the transform changes or the vector sizes do not match current mesh asset data.

### Transform Detection

Start conservative. A byte compare is acceptable because the matrix values are assigned directly before draw:

```cpp
bool SameMatrix(const XMATRIX44& a, const XMATRIX44& b) {
  return std::memcmp(&a, &b, sizeof(XMATRIX44)) == 0;
}
```

If that is too brittle later, replace with a small epsilon compare.

### Bounds Conversion

Use local min/max only when building or refreshing the cache.

Pseudo-code:

```cpp
BoundsCE MakeLocalBounds(const AABB& box);
BoundsCE MakeLocalBounds(const t850::AABB& box);

BoundsCE TransformBoundsCE(const BoundsCE& local, const XMATRIX44& world) {
  BoundsCE out;
  out.cx = local.cx*world.m11 + local.cy*world.m21 + local.cz*world.m31 + world.m41;
  out.cy = local.cx*world.m12 + local.cy*world.m22 + local.cz*world.m32 + world.m42;
  out.cz = local.cx*world.m13 + local.cy*world.m23 + local.cz*world.m33 + world.m43;
  out.ex = std::fabs(world.m11)*local.ex + std::fabs(world.m21)*local.ey + std::fabs(world.m31)*local.ez;
  out.ey = std::fabs(world.m12)*local.ex + std::fabs(world.m22)*local.ey + std::fabs(world.m32)*local.ez;
  out.ez = std::fabs(world.m13)*local.ex + std::fabs(world.m23)*local.ey + std::fabs(world.m33)*local.ez;
  return out;
}
```

The hot classifier should receive world-space `BoundsCE` and should not touch the world matrix.

### Plane-Mask Propagation

Today Phase 2 only propagates `Inside` to skip all children. Phase 3 should also propagate the subset of planes that matter for intersecting parents.

Add a classifier that returns both result and plane mask:

```cpp
FrustumClassifyResult ClassifyWorldBoundsFrustum(const BoundsCE& bounds,
                                                 const XVECTOR3 planes[6],
                                                 uint8_t activePlaneMask = 0x3F);
```

Rules:

```text
For each plane bit in activePlaneMask:
  dist = dot(plane.xyz, center) + plane.w
  radius = abs(plane.x)*ex + abs(plane.y)*ey + abs(plane.z)*ez

  if dist + radius < 0:
    Outside

  if dist - radius < 0:
    keep that plane bit in child planeMask

If no plane bits remain:
  Inside
Else:
  Intersecting with planeMask = remaining bits
```

Use parent masks like this:

```text
meshResult = classify(meshBounds, planes, 0x3F)

if mesh Outside: skip mesh
if mesh Inside: skip subset and cluster tests
if mesh Intersecting:
  subsetResult = classify(subsetBounds, planes, meshResult.planeMask)

if subset Outside: skip subset
if subset Inside: skip cluster tests
if subset Intersecting:
  clusterResult = classify(clusterBounds, planes, subsetResult.planeMask)
```

This is safe because child bounds are contained by parent bounds. If a parent is fully inside a plane, every child is also inside that plane, so children do not need to test it.

### Stats To Add

Add enough stats to prove Phase 3 did what it was supposed to do:

```cpp
mutable unsigned long long m_cullingPlaneTests = 0;
mutable unsigned long long m_cullingSkippedSubsetTests = 0;
mutable unsigned long long m_cullingSkippedClusterTests = 0;
mutable unsigned long long m_cullingBoundsCacheRefreshes = 0;
```

Report them in `DayScene::WriteBenchmarkResults` under `cullingStats.latest`, `averagePerFrame`, and `totals`.

Expected outcome:

```text
clusterTests may stay near Phase 2 if camera path is similar
planeTests should drop
cullingCpuMs should drop below Phase 2's ~0.04 ms when benchmark timing noise allows
```

If `cullingCpuMs` does not improve, inspect whether cache refresh happens every frame. Static Sponza should not rebuild world bounds every draw unless transform changes.

### Files To Touch

Expected Phase 3A files:

```text
T850\Framework\include\scene\RenderMesh.h
T850\Framework\src\scene\RenderMesh.cpp
T850\DayScene\DayScene.h
T850\DayScene\DayScene.cpp
```

Avoid touching backend driver files for Phase 3A. The work is CPU-side and should stay API-agnostic.

## Phase 3B: Spatial Cluster Reorder

Only start this after Phase 3A is validated.

### Current Limitation

Current clusters are contiguous chunks of the existing subset index order. That is safe but not necessarily spatially tight. If source triangle order jumps around the scene, a cluster AABB can still be too large.

### Goal

Build tighter clusters inside each material subset by reordering triangles within that subset before uploading the shared index pool.

Material boundaries must stay intact.

### Algorithm

For each subset:

1. Build triangle records from the subset's indices.
2. Compute triangle centroid from vertex positions.
3. Recursively split by the longest centroid-bounds axis until each leaf has at most `kTargetTrianglesPerCluster` triangles.
4. Emit triangles leaf by leaf into a reordered temporary index array.
5. Upload that reordered array to the existing shared index pool.
6. Build `SubmeshCluster` ranges that point at contiguous leaf ranges.

Do not allocate one index buffer per cluster. Use the existing shared index pool and `DrawIndexed(count, sub_info->ibPoolAlloc.offsetElems + cluster.indexOffset, baseVertex)`.

### Validation Risks

Potential regressions:

```text
wrong winding if triangle order is accidentally changed within a triangle
wrong startIndex/baseVertex math
alpha/transmission subset accidentally clustered across material boundary
draw call count too high from too-small clusters
cluster AABBs not matching reordered indices
```

Keep `kTargetTrianglesPerCluster` conservative, likely `128` or `256`, and compare visual output with culling disabled.

## Benchmark Protocol

Start with D3D12 because it was the problematic API before Phase 1.

Use offscreen mode:

```powershell
Set-Location -Path 'D:\Code\Game\T850\T850\bin\x64\Release'
.\DayScene.exe --benchmark --offscreen --api d3d12 --width 2560 --height 1440 --scene 1 --logLevel 0 --benchmarkOutput <out>\benchmark_stats_dayscene_d3d12_2560x1440_culling_on.json
.\DayScene.exe --benchmark --offscreen --cullDisabled --api d3d12 --width 2560 --height 1440 --scene 1 --logLevel 0 --benchmarkOutput <out>\benchmark_stats_dayscene_d3d12_2560x1440_culling_off.json
```

Run one pair in normal order and one pair in reversed order:

```text
off -> on
on -> off
```

Before each benchmark pair, verify no hidden benchmark process is alive:

```powershell
Get-CimInstance Win32_Process -Filter "Name='DayScene.exe'"
```

Do not trust results if multiple `DayScene.exe` processes are running.

After D3D12 validation, run the full API matrix:

```text
d3d11 off/on
d3d12 off/on
vulkan off/on
gl off/on with --glOffscreenFlushMode frame
```

Avoid `--glOffscreenFlushMode none`; it previously crashed after roughly 14-15 seconds in ANGLE/GLES3 on Windows.

## Acceptance Criteria

Phase 3A is complete when:

```text
Release x64 DayScene builds cleanly
diagnostics report no errors on touched files
6 and KP6 still toggle culling at runtime
drawn index counts match Phase 2 for the same benchmark path
cluster draw calls remain in the same expected range as Phase 2
culling CPU is no worse than Phase 2 in paired/reversed D3D12 tests
plane-test or skipped-test stats prove the new mask/cache path is active
```

Phase 3B is complete when, in addition:

```text
visual output matches with culling enabled/disabled except for intentional hidden geometry omission
cluster AABBs are tighter or cull more indices than Phase 2
full API matrix has no crashes
draw call growth does not erase the culling benefit
```

## Repository Hygiene

Before committing Phase 3, run:

```powershell
Set-Location -Path 'D:\Code\Game\T850'
git status --short
```

Do not stage:

```text
T850\Assets\Models\*.glb local additions
benchmark output folders unless explicitly requested
T850\config.json if it only contains local runtime preferences
```

Commit source, scripts, scene JSON, and documentation that are part of the feature.